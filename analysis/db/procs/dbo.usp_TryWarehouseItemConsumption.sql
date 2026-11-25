-- ===========================================================
-- Title		:	usp_TryWarehouseItemConsumption
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-12
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_TryWarehouseItemConsumption
(
	@LabelID			BIGINT,
	@ItemInstanceID		BIGINT,
	@OwnerAccountID		UNIQUEIDENTIFIER
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		DECLARE	@ownerAccountIdStr	NCHAR(36)	= CAST(@OwnerAccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	ISNULL(@LabelID, -1) <= 0
				OR	ISNULL(@ItemInstanceID, -1) <= 0
				OR	@OwnerAccountID IS NULL		)
			RAISERROR(N'invalid parameter, labelId:0x%I64X, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @LabelID, @ItemInstanceID, @ownerAccountIdStr);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			IF	NOT EXISTS	(	SELECT	1
								FROM	dbo.WarehouseGoods
								WHERE	LabelID				= @LabelID
								AND		OwnerAccountID		= @OwnerAccountID
								AND		RegistrationState	= 2		)
				RAISERROR(N'there is no such warehouse goods or invalid registration state, labelId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @LabelID, @ownerAccountIdStr);
			
			UPDATE	dbo.WarehouseItem
			SET		ItemState			= 2,	-- Exportation Try
					UserServiceType		= 2,	-- Lobby Service
					LastStateUpdateTime	= GETDATE()
			WHERE	ItemInstanceID		= @ItemInstanceID
			AND		LabelID				= @LabelID
			AND		OwnerAccountID		= @OwnerAccountID
			AND		ItemState			= 1;	-- Usable
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'failed to try warehouse item consumption; there is no such item or invalid item state, labelId:0x%I64X, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																													@LabelID, @ItemInstanceID, @ownerAccountIdStr);
		END
		
		IF	(@INIT_TRANCOUNT = 0)
			COMMIT;
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
